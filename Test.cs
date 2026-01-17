namespace Meow
{
    public class Program 
    {
        public static void Main()
        {
            Console.WriteLine("Hello, Meow!");
            while (true)
            {
                string cmd = Console.ReadLine();
                if (cmd == "exit") break;
                Console.WriteLine($"You entered: {cmd}");
            }
        }
    }
}